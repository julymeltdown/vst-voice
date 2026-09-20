import copy
import unittest
from unittest.mock import patch
import torch

from tools.voice_model_training.vocoder_warm_start import initialize
from tools.voice_model_training.test_train_vocoder_command import settings
from tools.voice_model_training.train_vocoder import model_settings, OBJECTIVE_ID
from tools.voice_model_training.unvoiced_periodicity import OBJECTIVE_ID as PERIODIC_OBJECTIVE


class WarmStartTests(unittest.TestCase):
    def fixture(self):
        old=dict(settings(),schemaVersion=3,architectureProfile='mini-nsf-32-smoke-v1',trainingSegmentFrames=512)
        new=dict(old,schemaVersion=4,objectiveId=PERIODIC_OBJECTIVE,learningRate=.00001)
        common=dict(configuration=model_settings(old),trainingRevision='pinned',torchVersion='captured')
        metadata=dict(common,settings=new,trainingConfigurationSha256='n'*64)
        previous=dict(run=dict(common,settings=old,trainingConfigurationSha256='o'*64,
            completedEpochs=2,parentReceiptSha256='p'*64),datasetSha256='d'*64,
            profileSha256='f'*64,objectiveId=OBJECTIVE_ID)
        g,d=torch.nn.Linear(2,2),torch.nn.Linear(2,2)
        owners=torch.nn.ModuleDict(dict(generator=g,discriminator_0=d))
        weights={k:torch.full_like(v,.25) for k,v in owners.state_dict().items()}
        state=dict(metadata=previous,epoch=dict(epochComplete=True,coverageVerified=True,
            datasetSha256='d'*64,profileSha256='f'*64,objectiveId=OBJECTIVE_ID),model=weights)
        receipt=dict(formatId='com.project-seam.gan-checkpoint',checkpointSha256='c'*64)
        go,do=torch.optim.AdamW(g.parameters(),lr=.00001),torch.optim.AdamW(d.parameters(),lr=.00001)
        return (g,[d],go,do,'checkpoint','a'*64),dict(metadata=metadata,profile='f'*64,dataset='d'*64,
                                                   objective=PERIODIC_OBJECTIVE),state,receipt

    def test_weights_only_reset_and_origin(self):
        args,kwargs,state,receipt=self.fixture()
        with patch('tools.voice_model_training.vocoder_warm_start.load_local_checkpoint',return_value=(state,receipt)):
            result=initialize(*args,**kwargs)
        self.assertTrue(torch.equal(args[0].weight,torch.full_like(args[0].weight,.25)))
        self.assertTrue(torch.equal(args[1][0].weight,torch.full_like(args[1][0].weight,.25)))
        self.assertEqual(args[2].state,{})
        self.assertEqual(result['sourceCompletedEpochs'],2)
        self.assertTrue(result['optimizerReset']);self.assertTrue(result['rngReset'])
        expected=torch.Generator().manual_seed(kwargs['metadata']['settings']['seed'])
        self.assertTrue(torch.equal(torch.rand(3),torch.rand(3,generator=expected)))

    def test_changed_dataset_settings_and_partial_rejected_before_mutation(self):
        for change in ('dataset','partial','weights'):
            args,kwargs,state,receipt=self.fixture();before=args[0].weight.detach().clone()
            if change=='dataset':kwargs['dataset']='wrong'
            elif change=='partial':state['epoch']['epochComplete']=False
            else:state['model']['generator.weight'][0,0]=float('nan')
            with patch('tools.voice_model_training.vocoder_warm_start.load_local_checkpoint',return_value=(state,receipt)):
                with self.assertRaises(ValueError):initialize(*args,**kwargs)
            self.assertTrue(torch.equal(before,args[0].weight))

    def test_governed_seed_change_reseeds_and_records_lineage(self):
        args,kwargs,state,receipt=self.fixture()
        kwargs['metadata']['settings']['seed']+=1
        with patch('tools.voice_model_training.vocoder_warm_start.load_local_checkpoint',return_value=(state,receipt)):
            result=initialize(*args,**kwargs)
        self.assertTrue(result['seedChanged'])
        self.assertEqual(result['sourceSeed'],kwargs['metadata']['settings']['seed']-1)
        expected=torch.Generator().manual_seed(kwargs['metadata']['settings']['seed'])
        self.assertTrue(torch.equal(torch.rand(3),torch.rand(3,generator=expected)))

    def test_same_seed_reports_unchanged(self):
        args,kwargs,state,receipt=self.fixture()
        with patch('tools.voice_model_training.vocoder_warm_start.load_local_checkpoint',return_value=(state,receipt)):
            result=initialize(*args,**kwargs)
        self.assertFalse(result['seedChanged'])

    def test_dirty_optimizer_rejected(self):
        args,kwargs,state,receipt=self.fixture();args[2].state[args[0].weight]['step']=1
        with patch('tools.voice_model_training.vocoder_warm_start.load_local_checkpoint',return_value=(state,receipt)):
            with self.assertRaisesRegex(ValueError,'fresh optimizers'):initialize(*args,**kwargs)


if __name__=='__main__':unittest.main()
