import unittest
import torch
from tools.voice_model_training.unvoiced_periodicity import periodicity_loss, phone_mask


class PeriodicityTests(unittest.TestCase):
    def test_phone_mask_clips_segments_and_excludes_padding_silence(self):
        entry=dict(score=dict(language='ja'),label=dict(frameCount=3000,phonemes=[
            dict(startFrame=0,endFrame=1000,symbol='s'),
            dict(startFrame=1000,endFrame=2000,symbol='a'),
            dict(startFrame=2000,endFrame=3000,symbol='pau')]))
        mask=phone_mask(entry,sample_offset=500,sample_count=3000,valid_samples=2500)
        self.assertEqual(int(mask.sum()),500)
        self.assertTrue(mask[:,:,:500].all())
        self.assertFalse(mask[:,:,500:].any())
        entry['score']['language']='en'
        with self.assertRaises(ValueError):phone_mask(entry,sample_offset=0,sample_count=3000,valid_samples=3000)

    def fixture(self):
        generator = torch.Generator().manual_seed(71)
        target = torch.randn(1,1,4096,generator=generator)*.02
        predicted = (.02*torch.sin(torch.arange(4096)*2*torch.pi/256)).reshape(1,1,-1).requires_grad_()
        mask = torch.ones_like(target,dtype=torch.bool)
        return predicted,target,mask

    def test_target_identity_and_periodic_artifact_gradient(self):
        p,t,m = self.fixture()
        equal,info = periodicity_loss(t,t,m)
        self.assertLess(float(equal),1e-10)
        loss,info = periodicity_loss(p,t,m)
        self.assertGreater(float(loss.detach()),.5)
        loss.backward()
        self.assertTrue(torch.isfinite(p.grad).all())
        self.assertGreater(float(p.grad.abs().sum()),0)
        self.assertEqual(info['selectedWindows'],13)

    def test_voiced_samples_and_reference_have_no_gradient(self):
        p,t,m = self.fixture();t.requires_grad_();m[:,:,2048:]=False
        loss,info=periodicity_loss(p,t,m);loss.backward()
        self.assertEqual(info['selectedWindows'],5)
        self.assertEqual(float(p.grad[:,:,2048:].abs().sum()),0)
        self.assertIsNone(t.grad)

    def test_no_windows_silence_and_collapse(self):
        p,t,m=self.fixture()
        loss,info=periodicity_loss(p,t,~m);loss.backward()
        self.assertEqual(float(loss.detach()),0);self.assertEqual(info['selectedWindows'],0)
        loss,info=periodicity_loss(p,torch.zeros_like(t),m)
        self.assertEqual(float(loss.detach()),0);self.assertEqual(info['selectedWindows'],0)
        loss,_=periodicity_loss(torch.zeros_like(p),t,m)
        self.assertGreater(float(loss),.9)

    def test_invalid_inputs(self):
        p,t,m=self.fixture()
        for bad in (m.float(),m[:,:,:1000]):
            with self.assertRaises(ValueError):periodicity_loss(p,t,bad)
        p=p.detach();p[0,0,0]=float('nan')
        with self.assertRaises(ValueError):periodicity_loss(p,t,m)


if __name__=='__main__':unittest.main()
